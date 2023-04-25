#ifndef _BUS_H_
#define _BUS_H_
#include "main.hpp"
#include "busstate.hpp"
#include "arbitration.hpp"
#include "queue"
#include "atomic"

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
    size_t write(uint8_t symbol);
    int    availableForWrite();

    std::atomic<int> _nbrRestarts1;
    std::atomic<int> _nbrRestarts2;
    std::atomic<int> _nbrArbitrations;
    std::atomic<int> _nbrLost1;
    std::atomic<int> _nbrLost2;
    std::atomic<int> _nbrWon1;
    std::atomic<int> _nbrWon2;
    std::atomic<int> _nbrErrors;
  private:
    inline void push    (const data& d);
           void receive (uint8_t symbol, unsigned long startBitTime);
    BusState     _busState;
    Arbitration  _arbitration;
    WiFiClient*  _client;

#if USE_ASYNCHRONOUS
    // handler to be notified when there is signal change on the serial input
    static void IRAM_ATTR receiveHandler();

    // queue from Bus to read method
    QueueHandle_t _queue;

    // task to read bytes form the serial object and process them with receive methods
    TaskHandle_t  _serialEventTask;
    
    static void readDataFromSoftwareSerial(void *args);
#else
      std::queue<data> _queue;
#endif    
};

extern BusType Bus;

#endif
