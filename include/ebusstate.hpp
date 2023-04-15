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

    inline void data(uint8_t symbol)
    {
        switch (_state)
        {
        case eStartup:
            _state = symbol == SYN ? syn(eReceivedFirstSYN) : eStartup;
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
            _state = symbol == SYN ? synerror(_state, eStartup) : eReceivedAddressAfterSecondSYN;
            _master = symbol;
            break;
        case eReceivedAddressAfterSecondSYN:
            _state = symbol == SYN ? synerror(_state, eStartup) : eBusy;
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
    eState synerror(eState currentstate, eState newstate)
    {
        unsigned long lastsyn = passedsincesyn();
        _SYNtime = micros();
        DEBUG_LOG ("unexpected SYN on bus while state is %s, setting state to %s m=0x%02x, b=0x%02x %ld us\n", enumvalue(currentstate), enumvalue(newstate), _master, _byte, lastsyn);
        return newstate;
    }

    void reset()
    {
        _state = eStartup;
    }

    unsigned long passedsincesyn()
    {
        return micros() - _SYNtime;

    }

    eState  _state;
    uint8_t _master;
    uint8_t _byte;   
    unsigned long _SYNtime;
};
#endif
