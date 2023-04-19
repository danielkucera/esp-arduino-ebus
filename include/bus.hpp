#ifndef _BUS_H_
#define _BUS_H_
#include "main.hpp"
#include "queue"

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
      };
    BusType();
    ~BusType();

    bool read(data& d);

  private:
    void push(data& d);
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
