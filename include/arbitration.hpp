#ifndef _ARBITRATION_H_
#define _ARBITRATION_H_

#include "ebusstate.hpp"

class Arbitration
{
    public:
        enum state {none,  // no arbitration ongoing/not yet completed
                    arbitrating, // arbitration ongoing
                    won,   // won
                    lost,  // lost
                    error  // error
                    };

        Arbitration()
        : _arbitrating(false)
        , _participateSecond(false)
        , _arbitration_address(0)
        {}

    bool               start  (EBusState& busstate, uint8_t master);
    Arbitration::state data   (EBusState& busstate, uint8_t symbol);

    private:
        bool    _arbitrating;
        bool    _participateSecond;
        uint8_t _arbitration_address;
};

#endif
