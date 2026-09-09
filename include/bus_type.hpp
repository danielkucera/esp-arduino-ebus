#pragma once

#include <cstdint>
#include <queue>

#include "arbitration.hpp"
#include "bus_state.hpp"

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

void getArbitrationClient(int& clientFd, uint8_t& address);
void clearArbitrationClient();
bool setArbitrationClient(int& clientFd, uint8_t& address);

void arbitrationDone();
int arbitrationRequested(uint8_t& address);

#include "atomic"
#define ATOMIC_INT std::atomic<int>

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
    bool enhanced;         // is this an enhanced command?
    uint8_t c;             // command byte, only used when in "enhanced" mode
    uint8_t d;             // data byte for both regular and enhanced command
    int client_fd;         // the client fd that is being arbitrated
    int log_to_client_fd;  // the client fd that needs to log
  };
  BusType();
  ~BusType();

  // begin and end, like with Serial
  void begin();
  void end();

  // Is there a value available that should be send to a client?
  bool read(data& d);
  static size_t write(uint8_t symbol);
  static int availableForWrite();
  int available();

  // std::atomic seems not well supported on esp12e, besides it is also not
  // needed there
  ATOMIC_INT nbr_restarts_1_;
  ATOMIC_INT nbr_restarts_2_;
  ATOMIC_INT nbr_arbitrations_;
  ATOMIC_INT nbr_lost_1_;
  ATOMIC_INT nbr_lost_2_;
  ATOMIC_INT nbr_won_1_;
  ATOMIC_INT nbr_won_2_;
  ATOMIC_INT nbr_errors_;
  ATOMIC_INT nbr_late_;

 private:
  inline void push(const data& d);
  void receive(uint8_t symbol, uint32_t startBitTime);
  BusState bus_state_;
  Arbitration arbitration_;
  int client_fd_;

#if USE_ASYNCHRONOUS
  // handler to be notified when there is signal change on the serial input
  static void IRAM_ATTR receiveHandler();

  // queue from Bus to read method
  QueueHandle_t queue_;

  // task to read bytes form the serial object and process them with receive
  // methods
  TaskHandle_t serial_event_task_;

  static void readDataFromSoftwareSerial(void* args);
#else
  std::queue<data> queue_;
#endif
};

extern BusType Bus;
