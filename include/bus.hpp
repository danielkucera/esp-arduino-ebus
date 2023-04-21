#ifndef _BUS_H_
#define _BUS_H_
#include "main.hpp"
#include "busstate.hpp"
#include "arbitration.hpp"
#include "queue"

// This object retrieves data from the Serial object and let's
// it flow through the arbitration process. The "read" method 
// will return data with meta information that tells what should 
// be done with the returned data. This object hides if the 
// underlying implementation is synchronous or asynchronous
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
        WiFiClient* _logtoclient;   // the client that needs to log
      };
    BusType();
    ~BusType();

    // begin and end, like with Serial
    void begin();
    void end();

    // Is there a value available that should be send to a client?
    bool   read(data& d);
    size_t write(uint8_t c);
    int    availableForWrite();

  private:
    inline void push    (const data& d);
           void receive (uint8_t byte);
    BusState     _busState;
    Arbitration  _arbitration;
    WiFiClient*  _client;
#if USE_ASYNCHRONOUS
    QueueHandle_t _queue;
    static void OnReceiveCB();
    static void OnReceiveErrorCB(hardwareSerial_error_t e);
#else
      std::queue<data> _queue;
#endif    
};

extern BusType Bus;

#endif
