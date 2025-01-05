#pragma once

#include <WiFiClient.h>

#include <queue>

#include "arbitration.hpp"
#include "busstate.hpp"

enum responses {
  RESETTED = 0x0,
  RECEIVED = 0x1,
  STARTED = 0x2,
  INFO = 0x3,
  FAILED = 0xa,
  ERROR_EBUS = 0xb,
  ERROR_HOST = 0xc
};

enum errors { ERR_FRAMING = 0x00, ERR_OVERRUN = 0x01 };

void getArbitrationClient(WiFiClient*& client, uint8_t& address);
void clearArbitrationClient();
bool setArbitrationClient(WiFiClient*& client, uint8_t& address);

void arbitrationDone();
WiFiClient* arbitrationRequested(uint8_t& address);

#ifdef ESP32
#include "atomic"
#define ATOMIC_INT std::atomic<int>
#else
#define ATOMIC_INT int
#endif
// This object retrieves data from the Serial object and let's
// it flow through the arbitration process. The "read" method
// will return data with meta information that tells what should
// be done with the returned data. This object hides if the
// underlying implementation is synchronous or asynchronous
class BusType {
 public:
  // "receive" data should go to all clients that are not in arbitration mode
  // "enhanced" data should go only to the arbitrating client
  // a client is in arbitration mode if _client is not null
  struct data {
    bool _enhanced;       // is this an enhanced command?
    uint8_t _c;           // command byte, only used when in "enhanced" mode
    uint8_t _d;           // data byte for both regular and enhanced command
    WiFiClient* _client;  // the client that is being arbitrated
    WiFiClient* _logtoclient;  // the client that needs to log
  };
  BusType();
  ~BusType();

  // begin and end, like with Serial
  void begin();
  void end();

  // Is there a value available that should be send to a client?
  bool read(data& d);
  size_t write(uint8_t symbol);
  int availableForWrite();
  int available();

  // std::atomic seems not well supported on esp12e, besides it is also not
  // needed there
  ATOMIC_INT _nbrRestarts1;
  ATOMIC_INT _nbrRestarts2;
  ATOMIC_INT _nbrArbitrations;
  ATOMIC_INT _nbrLost1;
  ATOMIC_INT _nbrLost2;
  ATOMIC_INT _nbrWon1;
  ATOMIC_INT _nbrWon2;
  ATOMIC_INT _nbrErrors;
  ATOMIC_INT _nbrLate;

 private:
  inline void push(const data& d);
  void receive(uint8_t symbol, uint32_t startBitTime);
  BusState _busState;
  Arbitration _arbitration;
  WiFiClient* _client;

#if USE_ASYNCHRONOUS
  // handler to be notified when there is signal change on the serial input
  static void IRAM_ATTR receiveHandler();

  // queue from Bus to read method
  QueueHandle_t _queue;

  // task to read bytes form the serial object and process them with receive
  // methods
  TaskHandle_t _serialEventTask;

  static void readDataFromSoftwareSerial(void* args);
#else
  std::queue<data> _queue;
#endif
};

extern BusType Bus;
