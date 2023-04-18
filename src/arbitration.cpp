#include "arbitration.hpp"
#include "ebusstate.hpp"

bool Arbitration::start(EBusState& busstate, uint8_t master)
{   static int arb = 0;
    if (_arbitrating) {
        return false;
    }
    if (master == SYN) {
        return false;
    }
    if (busstate._state != EBusState::eReceivedFirstSYN) {
        return false;
    }
    DEBUG_LOG("ARB START %04i 0x%02x %ld us\n", arb++, master, busstate.microsSinceLastSyn());
                
    // too late if we don't have enough time to send our symbol
    // assume we need at least 20 us to send the symbol
    unsigned long now =  busstate.microsSinceLastSyn();
    if (Serial.available() != 0 || now>((4456-20)-4160)) 
    {
        // if we are too late, don't try to participate and retry next round
        DEBUG_LOG("ARB LATE 0x%02x %ld us\n", Serial.peek(), now);
        return false;
    }
    Serial.write(master);

    _arbitration_address = master;
    _arbitrating = true;
    _participateSecond = false;
    return true;
}

// arbitration is timing sensitive. avoid communicating with WifiClient during arbitration
// according https://ebus-wiki.org/lib/exe/fetch.php/ebus/spec_test_1_v1_1_1.pdf section 3.2
//   "Calculated time distance between start bit of SYN byte and 
//   bus permission must be in the range of 4300 us - 4456,24 us ."
// rely on the uart to keep the timing
// just make sure the byte to send is available in time
Arbitration::state Arbitration::data(EBusState& busstate, uint8_t symbol) {
    static int arb = 0;
    if (!_arbitrating){
        return none;
    }
    switch (busstate._state) 
    {
    case EBusState::eStartup: // error out
    case EBusState::eStartupFirstSyn:
    case EBusState::eStartupSymbolAfterFirstSyn:
    case EBusState::eStartupSecondSyn:
    case EBusState::eReceivedFirstSYN:
        DEBUG_LOG("ARB ERROR      0x%02x 0x%02x\n", busstate._master, busstate._byte);
        //send_res(client, ERROR_EBUS, ERR_FRAMING);
        _arbitrating = false;
        return error;
    case EBusState::eReceivedAddressAfterFirstSYN: // did we win 1st round of abitration?
        if (symbol == _arbitration_address) {
            DEBUG_LOG("ARB WON1       0x%02x %ld us\n", symbol, busstate.microsSinceLastSyn());
            _arbitrating = false;
            return won; // we won; nobody else will write to the bus
        } else if ((symbol & 0b00001111) == (_arbitration_address & 0b00001111)) { 
            DEBUG_LOG("ARB PART SECND 0x%02x 0x%02x\n", _arbitration_address, symbol);
            _participateSecond = true; // participate in second round of arbitration if we have the same priority class
        }
        else {
            DEBUG_LOG("ARB LOST1      0x%02x %ld us\n", symbol, busstate.microsSinceLastSyn());
            // arbitration might be ongoing between other bus participants, so we cannot yet know what 
            // the winning master is. Need to wait for eBusy
        }
        return arbitrating;
    case EBusState::eReceivedSecondSYN: // did we sign up for second round arbitration?
        if (_participateSecond) {
            // execute second round of arbitration
            DEBUG_LOG("ARB MASTER2    0x%02x %ld us\n", _arbitration_address, busstate.microsSinceLastSyn());
            Serial.write(_arbitration_address);
        }
        else {
            DEBUG_LOG("ARB SKIP       0x%02x %ld us\n", _arbitration_address, busstate.microsSinceLastSyn());
        }
        return arbitrating;
    case EBusState::eReceivedAddressAfterSecondSYN: // did we win 2nd round of arbitration?
        if (symbol == _arbitration_address) {
            DEBUG_LOG("ARB WON2       0x%02x %ld us\n", symbol, busstate.microsSinceLastSyn());
            _arbitrating = false;
            return won; // we won; nobody else will write to the bus
        }
        else {
            DEBUG_LOG("ARB LOST2      0x%02x %ld us\n", symbol, busstate.microsSinceLastSyn());
            // we now know which address has won and we could exit here. 
            // but it is easier to wait for eBusy, so after the while loop, the 
            // "lost" state can be handled the same as when somebody lost in the first round
        }
        return arbitrating;
    case EBusState::eBusy:
        _arbitrating = false;
        return lost;            
    }
    return arbitrating;
}
