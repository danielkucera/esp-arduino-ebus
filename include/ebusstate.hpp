#ifndef _EBUSSTATE_H_
#define _EBUSSTATE_H_
#include "main.hpp"
#include "enhanced.hpp"


// Implements the state of the bus
// The arbitration process can only start at well defined states of the bus
// To asses the state, all data received on the bus needs to be send to this object
// The object takes care of startup of the bus and recovery when an unexpected event 
// happens 
class EBusState {
public:
    enum eState {
        eStartup,                      // In startup mode to analyze bus state
        eStartupFirstSyn,              // Either the bus is busy, it is arbitrating, or it is free to start an arbitration
        eStartupSymbolAfterFirstSyn,   
        eStartupSecondSyn,
        eReceivedFirstSYN,             // Received SYN
        eReceivedAddressAfterFirstSYN, // Received SYN ADDRESS
        eReceivedSecondSYN,            // Received SYN ADDRESS SYN
        eReceivedAddressAfterSecondSYN,// Received SYN ADDRESS SYN ADDRESS
        eBusy                          // Bus is busy; _master is master that won, _byte is first symbol after the master address
    };
    static const char* enumvalue(eState e)
    {
        const char* values[]  = {
          "eStartup",
          "eStartupFirstSyn",
          "eStartupSymbolAfterFirstSyn",
          "eStartupSecondSyn",
          "eReceivedFirstSYN",
          "eReceivedAddressAfterFirstSYN",
          "eReceivedSecondSYN",
          "eReceivedAddressAfterSecondSYN",
          "eBusy"
        };
        return values[e];
    }
    EBusState()
        : _state(eStartup)
    {}
    // Evaluate a symbol received on UART and determine what the new state of the bus is
    inline void data(uint8_t symbol)
    {
        switch (_state)
        {
        case eStartup:
            _state = symbol == SYN ? syn(eStartupFirstSyn) : eStartup;
            break;
        case eStartupFirstSyn:
            _state = symbol == SYN ? syn(eReceivedFirstSYN) : eStartupSymbolAfterFirstSyn;
            break;
        case eStartupSymbolAfterFirstSyn:
            _state = symbol == SYN ? syn(eStartupSecondSyn) : eBusy;
            break;       
        case eStartupSecondSyn:
            _state = symbol == SYN ? syn(eReceivedFirstSYN) : eBusy;
            break;          
        case eReceivedFirstSYN:
            _state = symbol == SYN ? syn(eReceivedFirstSYN) : eReceivedAddressAfterFirstSYN;
            _master = symbol;
            break;
        case eReceivedAddressAfterFirstSYN:
            _state = symbol == SYN ? syn(eReceivedSecondSYN ): eBusy;
            _byte = symbol;
            break;
        case eReceivedSecondSYN:
            _state = symbol == SYN ? error(_state, eReceivedFirstSYN) : eReceivedAddressAfterSecondSYN;
            _master = symbol;
            break;
        case eReceivedAddressAfterSecondSYN:
            _state = symbol == SYN ? error(_state, eReceivedFirstSYN) : eBusy;
            _byte = symbol;
            break;            
        case eBusy:
            _state = symbol == SYN ? syn(eReceivedFirstSYN) : eBusy;
            break;
        }
    }
    inline eState syn(eState newstate)
    {
        _SYNtime = micros();
        return newstate;
    }
    eState error(eState currentstate, eState newstate)
    {
        unsigned long lastsyn = microsSinceLastSyn();
        _SYNtime = micros();
        DEBUG_LOG ("unexpected SYN on bus while state is %s, setting state to %s m=0x%02x, b=0x%02x %ld us\n", enumvalue(currentstate), enumvalue(newstate), _master, _byte, lastsyn);
        return newstate;
    }

    void reset()
    {
        _state = eStartup;
    }

    unsigned long microsSinceLastSyn()
    {
        return micros() - _SYNtime;

    }

    eState  _state;
    uint8_t _master;
    uint8_t _byte;   
    unsigned long _SYNtime;
};
#endif
