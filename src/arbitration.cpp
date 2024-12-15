#include "arbitration.hpp"

#include "bus.hpp"

// arbitration is timing sensitive. avoid communicating with WifiClient during
// arbitration according
// https://ebus-wiki.org/lib/exe/fetch.php/ebus/spec_test_1_v1_1_1.pdf
// section 3.2
//   "Calculated time distance between start bit of SYN byte and
//   bus permission must be in the range of 4300 us - 4456,24 us ."
// SYN symbol is 4167 us. If we would receive the symbol immediately,
// we need to wait (4300 - 4167)=133 us after we received the SYN.
Arbitration::result Arbitration::start(BusState& busstate, uint8_t master,
                                       unsigned long startBitTime) {
  static int arb = 0;
  if (_arbitrating) {
    return not_started;
  }
  if (master == SYN) {
    return not_started;
  }
  if (busstate._state != BusState::eReceivedFirstSYN) {
    return not_started;
  }

  // too late if we don't have enough time to send our symbol
  unsigned long now = micros();
  unsigned long microsSinceLastSyn = busstate.microsSinceLastSyn();
  unsigned long timeSinceStartBit = now - startBitTime;
  if (timeSinceStartBit > 4456 || Bus.available()) {
    // if we are too late, don't try to participate and retry next round
    DEBUG_LOG("ARB LATE 0x%02x %lu us\n", BusSer.peek(), timeSinceStartBit);
    return late;
  }
#if USE_ASYNCHRONOUS
  // When in async mode, we get immediately interrupted when a symbol is
  // received on the bus The earliest allowed to send is 4300 measured from the
  // start bit of the SYN command. We receive the exact flange of the startbit,
  // use that to calculate the exact time to wait. Then subtract time from the
  // wait to allow the uart to put the byte on the bus. Testing has shown this
  // requires about 700 micros on the esp32-c3.
  int delay = 4300 - timeSinceStartBit - 700;
  if (delay > 0) {
    delayMicroseconds(delay);
  }
#endif
  Bus.write(master);
  // Do logging of the ARB START message after writing the symbol, so enabled or
  // disabled logging does not affect timing calculations.
#if USE_ASYNCHRONOUS
  DEBUG_LOG("ARB START %04i 0x%02x %lu us %i  us\n", arb++, master,
            microsSinceLastSyn, delay);
#else
  DEBUG_LOG("ARB START %04i 0x%02x %lu us\n", arb++, master,
            microsSinceLastSyn);
#endif
  _arbitrationAddress = master;
  _arbitrating = true;
  _participateSecond = false;
  return started;
}

Arbitration::state Arbitration::data(BusState& busstate, uint8_t symbol,
                                     unsigned long startBitTime) {
  if (!_arbitrating) {
    return none;
  }
  switch (busstate._state) {
    case BusState::eStartup:  // error out
    case BusState::eStartupFirstSyn:
    case BusState::eStartupSymbolAfterFirstSyn:
    case BusState::eStartupSecondSyn:
    case BusState::eReceivedFirstSYN:
      DEBUG_LOG("ARB ERROR      0x%02x 0x%02x 0x%02x %lu us %lu us\n",
                busstate._master, busstate._symbol, symbol,
                busstate.microsSinceLastSyn(),
                busstate.microsSincePreviousSyn());
      _arbitrating = false;
      // Sometimes a second SYN is received instead of an address, either
      // after having started the arbitration, or after participating in
      // the second round of arbitration. This means the address we put on
      // the bus got lost. Most likely this is caused by not perfect timing
      // of the arbitration on our side, but could also be electrical
      // interference or wrong implementation in another bus participant. Try to
      // restart arbitration maximum 2 times
      if (_restartCount++ < 3 &&
          busstate._previousState == BusState::eReceivedFirstSYN)
        return restart1;
      if (_restartCount++ < 3 &&
          busstate._previousState == BusState::eReceivedSecondSYN)
        return restart2;
      _restartCount = 0;
      return error;
    case BusState::eReceivedAddressAfterFirstSYN:  // did we win 1st round of
                                                   // abitration?
      if (symbol == _arbitrationAddress) {
        DEBUG_LOG("ARB WON1       0x%02x %lu us\n", symbol,
                  busstate.microsSinceLastSyn());
        _arbitrating = false;
        _restartCount = 0;
        return won1;  // we won; nobody else will write to the bus
      } else if ((symbol & 0b00001111) == (_arbitrationAddress & 0b00001111)) {
        DEBUG_LOG("ARB PART SECND 0x%02x 0x%02x\n", _arbitrationAddress,
                  symbol);
        _participateSecond =
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
      if (_participateSecond && Bus.available() == 0) {
        // execute second round of arbitration
        unsigned long microsSinceLastSyn = busstate.microsSinceLastSyn();
#if USE_ASYNCHRONOUS
        // When in async mode, we get immediately interrupted when a symbol is
        // received on the bus The earliest allowed to send is 4300 measured
        // from the start bit of the SYN command. We receive the exact flange of
        // the startbit, use that to calculate the exact time to wait. Then
        // subtract time from the wait to allow the uart to put the byte on the
        // bus. Testing has shown this requires about 700 micros on the
        // esp32-c3.
        unsigned long timeSinceStartBit = micros() - startBitTime;
        int delay = 4300 - timeSinceStartBit - 700;
        if (delay > 0) {
          delayMicroseconds(delay);
        }
#endif
        // Do logging of the ARB START message after writing the symbol, so
        // enabled or disabled logging does not affect timing calculations.
        Bus.write(_arbitrationAddress);
        DEBUG_LOG("ARB MASTER2    0x%02x %lu us\n", _arbitrationAddress,
                  microsSinceLastSyn);
      } else {
        DEBUG_LOG("ARB SKIP       0x%02x %lu us\n", _arbitrationAddress,
                  busstate.microsSinceLastSyn());
      }
      return arbitrating;
    case BusState::eReceivedAddressAfterSecondSYN:  // did we win 2nd round of
                                                    // arbitration?
      if (symbol == _arbitrationAddress) {
        DEBUG_LOG("ARB WON2       0x%02x %lu us\n", symbol,
                  busstate.microsSinceLastSyn());
        _arbitrating = false;
        _restartCount = 0;
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
      _arbitrating = false;
      _restartCount = 0;
      return _participateSecond ? lost2 : lost1;
  }
  return arbitrating;
}
