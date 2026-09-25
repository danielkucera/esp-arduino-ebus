#include "arbitration.hpp"

#include <esp_rom_sys.h>
#include <esp_timer.h>

#include "bus_type.hpp"

// arbitration is timing sensitive. avoid communicating with WifiClient during
// arbitration according
// https://ebus-wiki.org/lib/exe/fetch.php/ebus/spec_test_1_v1_1_1.pdf
// section 3.2
//   "Calculated time distance between start bit of SYN byte and
//   bus permission must be in the range of 4300 us - 4456,24 us ."
// SYN symbol is 4167 us. If we would receive the symbol immediately,
// we need to wait (4300 - 4167)=133 us after we received the SYN.
Arbitration::result Arbitration::start(const BusState& busstate, uint8_t master,
                                       uint32_t startBitTime, bool timingValid) {
  if (arbitrating_) {
    return not_started;
  }
  if (master == SYN) {
    return not_started;
  }
  if (busstate.state_ != BusState::eReceivedFirstSYN) {
    return not_started;
  }

  if (!timingValid || !BusSer.writeArbitration(master, startBitTime)) {
    return late;
  }
  arbitration_address_ = master;
  arbitrating_ = true;
  participate_second_ = false;
  return started;
}

Arbitration::state Arbitration::data(BusState& busstate, uint8_t symbol,
                                     uint32_t startBitTime, bool timingValid) {
  if (!arbitrating_) {
    return none;
  }
  switch (busstate.state_) {
    case BusState::eStartup:  // error out
    case BusState::eStartupFirstSyn:
    case BusState::eStartupSymbolAfterFirstSyn:
    case BusState::eStartupSecondSyn:
    case BusState::eReceivedFirstSYN:
      DEBUG_LOG("ARB ERROR      0x%02x 0x%02x 0x%02x %lu us %lu us\n",
                busstate.master_, busstate.symbol_, symbol,
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
          busstate.previous_state_ == BusState::eReceivedFirstSYN)
        return restart1;
      if (restart_count_++ < 3 &&
          busstate.previous_state_ == BusState::eReceivedSecondSYN)
        return restart2;
      restart_count_ = 0;
      return error;
    case BusState::eReceivedAddressAfterFirstSYN:  // did we win 1st round of
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
    case BusState::eReceivedSecondSYN:  // did we sign up for second round
                                        // arbitration?
      if (participate_second_) {
        if (!timingValid ||
            !BusSer.writeArbitration(arbitration_address_, startBitTime)) {
          // Both rounds obey the same deadline. A missed second round must
          // not inject an address after another master has started a frame.
          Bus.nbr_late_++;
        }
      }
      return arbitrating;
    case BusState::eReceivedAddressAfterSecondSYN:  // did we win 2nd round of
                                                    // arbitration?
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
    case BusState::eBusy:
      arbitrating_ = false;
      restart_count_ = 0;
      return participate_second_ ? lost2 : lost1;
  }
  return arbitrating;
}
