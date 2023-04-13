#ifndef _EBUSSTATE_H_
#define _EBUSSTATE_H_
#include "main.hpp"

enum symbols {
    SYN = 0xAA
};

class EBusState {
public:
    enum eState {
        eStartup,                      // In startup mode to analyze bus state
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

    void data(uint8_t symbol)
    {
        switch (_state)
        {
        case eStartup:
            _state = symbol == SYN ? eReceivedFirstSYN : eStartup;
            break;
        case eReceivedFirstSYN:
            _state = symbol == SYN ? eReceivedFirstSYN : eReceivedAddressAfterFirstSYN;
            _master = symbol;
            break;
        case eReceivedAddressAfterFirstSYN:
            _state = symbol == SYN ? eReceivedSecondSYN : eBusy;
            _byte = symbol;
            break;
        case eReceivedSecondSYN:
            _state = symbol == SYN ? synerror(_state, eStartup) : eReceivedAddressAfterSecondSYN;
            _master = symbol;
            break;
        case eReceivedAddressAfterSecondSYN:
            _state = symbol == SYN ? synerror(_state, eStartup) : eBusy;
            _byte = symbol;
            break;            
        case eBusy:
            _state = symbol == SYN ? eReceivedFirstSYN : eBusy;
            break;
        }
    }

    eState synerror(eState currentstate, eState newstate)
    {
        // ("unexpected SYN on bus while state is %s, setting state to %s\n", enumvalue(currentstate), enumvalue(newstate));
        return newstate;
    }

    void reset()
    {
        _state = eStartup;
    }

    eState  _state;
    uint8_t _master;
    uint8_t _byte;   
};
#endif
