#ifndef _ARBITRATION_H_
#define _ARBITRATION_H_

#include "busstate.hpp"

// Implements the arbitration algorithm. Uses the state of the bus to decide what to do.
// Typical usage:
// - try to start the arbitration with "start" method
// - pass each received value on the bus to the "data" method
//   which will then tell you what the state of the arbitration is
class Arbitration
{
    public:
        enum state {none,        // no arbitration ongoing
                    arbitrating, // arbitration ongoing
                    won1,        // won
                    won2,        // won
                    lost1,       // lost
                    lost2,       // lost
                    error,       // error
                    restart1,    // restart the arbitration
                    restart2,    // restart the arbitration
                    };

        Arbitration()
        : _arbitrating(false)
        , _participateSecond(false)
        , _arbitrationAddress(0)
        , _restartCount(0)
        {}
    // Try to start arbitration for the specified master.
    // Return values:
    // - false : arbitration not started. Possible reasons:
    //           + the bus is not in a state that allows to start arbitration
    //           + another arbitration is already ongoing
    //           + the master address is SYN
    // - true  : arbitration started. Make sure to pass all bus data to this object through the "data" method
    bool               start  (BusState& busstate, uint8_t master, unsigned long startBitTime);

    // A symbol was received on the bus, what does this do to the arbitration state?
    // Return values:
    // - see description of state enum value
    Arbitration::state data   (BusState& busstate, uint8_t symbol, unsigned long startBitTime);

    private:
        bool    _arbitrating;
        bool    _participateSecond;
        uint8_t _arbitrationAddress;
        int     _restartCount;
};

#endif
