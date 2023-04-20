#ifndef _BUS_H_
#define _BUS_H_
#include "main.hpp"
#include "queue"

// This object retrieves data from the Serial object and
// let's it flow through the arbitration process.
// The "read" method will return data with meta information that tells what should be done
// with the returned data.
// This object hides if the underlying implementation is synchronous or asynchronous
class BusType
{
  public:
    // "receive" data should go to all clients that are not in arbitration mode
    // "enhanced" data should go only to the arbitrating client
    // a client is in arbitration mode if _client is not null
    struct data {
        bool        _enhanced; // is this an enhanced command?
        uint8_t     _c;        // command byte, only used when in "enhanced" mode
        uint8_t     _d;        // data byte for both regular and enhanced command
        WiFiClient* _client;   // the client that is being arbitrated
        bool        _log;
      };
    BusType();
    ~BusType();

    // Is there a value available that should be send to a client?
    bool read(data& d);

  private:
    inline void push    (const data& d);
           void receive (uint8_t byte);

#ifdef USE_ASYNCHRONOUS
      QueueHandle_t _queue;
      static void OnReceiveCB();
      static void OnReceiveErrorCB(hardwareSerial_error_t e);
#else
      std::queue<data> _queue;
#endif    
};

extern BusType Bus;

#endif
