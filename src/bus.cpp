#include "main.hpp"
#include "bus.hpp"
#include "enhanced.hpp"
#include "queue"

// Using SoftwareSerial we get notified through an interrupt
// exactly when the start bit is received. We can use this
// for the timing of the arbitration. Because SoftwareSerial
// cannot write and read at the same, we use SoftwareSerial only
// for reading. For writing we still use HardwareSerial
#if USE_ASYNCHRONOUS
#include <SoftwareSerial.h>
SoftwareSerial mySerial;
#endif

BusType Bus;

#define BAUD_RATE 2400
#define MAX_FRAMEBITS (1 + 8 + 1)
#define SERIAL_EVENT_TASK_STACK_SIZE 2048
#define SERIAL_EVENT_TASK_PRIORITY (configMAX_PRIORITIES-1)
#define SERIAL_EVENT_TASK_RUNNING_CORE -1

BusType::BusType()
  : _nbrRestarts1(0)
  , _nbrRestarts2(0)
  , _nbrArbitrations(0) 
  , _nbrLost1(0)
  , _nbrLost2(0)
  , _nbrWon1(0)
  , _nbrWon2(0)
  , _nbrErrors(0) 
  , _client(0) {
}

BusType::~BusType() {
  end();
}


#if USE_ASYNCHRONOUS
void IRAM_ATTR _receiveHandler() {
  unsigned long lastStartBit= micros();
  xQueueSendToBackFromISR(Bus._serialEventQueue, &lastStartBit, 0); 
  vPortYieldFromISR();
}

void BusType::readDataFromSoftwareSerial(void *args)
{
    for(;;) {
        //Waiting for UART event.
        unsigned long startBitTime = 0;
        if(xQueueReceive(Bus._serialEventQueue, (void * )&startBitTime, (portTickType)portMAX_DELAY)) {
          auto avail = mySerial.available();
          if ( !avail) {
            // event fired on start bit, wait until first stop bit of longest frame
            delayMicroseconds(1+ MAX_FRAMEBITS * 1000000 / BAUD_RATE);
            avail = mySerial.available();
          }
          if (avail){
              uint8_t symbol = mySerial.read();
              Bus.receive(symbol, startBitTime);
          }        
       }
    }
    vTaskDelete(NULL);
}
#endif

void BusType::begin() {

#ifdef ESP32
  Serial.begin(2400, SERIAL_8N1, -1, 20); // used for writing
  mySerial.begin(2400, SWSERIAL_8N1, 21, -1); // used for reading
  mySerial.enableTx(false);
  mySerial.enableIntTx(false);
#elif defined(ESP8266)
  Serial.begin(2400);
#endif

#if USE_ASYNCHRONOUS
  _queue = xQueueCreate(QUEUE_SIZE, sizeof(data));
  _serialEventQueue = xQueueCreate(QUEUE_SIZE, sizeof(unsigned long));
  xTaskCreateUniversal(BusType::readDataFromSoftwareSerial, "_serialEventQueue", SERIAL_EVENT_TASK_STACK_SIZE, this, SERIAL_EVENT_TASK_PRIORITY, &_serialEventTask, SERIAL_EVENT_TASK_RUNNING_CORE);
  mySerial.onReceive(_receiveHandler);
#else
  Serial.setRxBufferSize(RXBUFFERSIZE);
#endif    
}

void BusType::end() {
#if USE_ASYNCHRONOUS
  vQueueDelete(_queue);
  vQueueDelete(_serialEventQueue);
#endif 
}


int BusType::availableForWrite() {
  return Serial.availableForWrite();
}

size_t BusType::write(uint8_t symbol) {
  return Serial.write(symbol);
}

bool BusType::read(data& d) {
#if USE_ASYNCHRONOUS
    return xQueueReceive(_queue, &d, 0) == pdTRUE; 
#else
    if (Serial.available()){
        uint8_t symbol = Serial.read();
        receive(symbol, micros());
    }
    if (_queue.size() > 0) {
        d = _queue.front();
        _queue.pop();
        return true;
    }
    return false;
#endif
}

void BusType::push(const data& d){
#if USE_ASYNCHRONOUS
    xQueueSendToBack(_queue, &d, 0); 
#else
    _queue.push(d);
#endif
}

void BusType::receive(uint8_t symbol, unsigned long startBitTime) {
  _busState.data(symbol);
  Arbitration::state state = _arbitration.data(_busState, symbol, startBitTime);
  switch (state) {
    case Arbitration::restart1:
      _nbrRestarts1++;
      goto NONE;
    case Arbitration::restart2:
      _nbrRestarts2++;
      goto NONE;
    case Arbitration::none:
        NONE:
        uint8_t arbitration_address;
        _client = enhArbitrationRequested(arbitration_address);
        if (_client) {
          if (_arbitration.start(_busState, arbitration_address, startBitTime)) {   
            _nbrArbitrations++;  
            DEBUG_LOG("BUS START SUCC 0x%02x %ld us\n", symbol, _busState.microsSinceLastSyn());
          }
          else {
            DEBUG_LOG("BUS START WAIT 0x%02x %ld us\n", symbol, _busState.microsSinceLastSyn());
          }
        }
        push({false, RECEIVED, symbol, 0, _client}); // send to everybody. ebusd needs the SYN to get in the right mood
        break;
    case Arbitration::arbitrating:
        DEBUG_LOG("BUS ARBITRATIN 0x%02x %ld us\n", symbol, _busState.microsSinceLastSyn());
        push({false, RECEIVED, symbol, _client, _client}); // do not send to arbitration client
        break;
    case Arbitration::won1:
        _nbrWon1++;
        goto WON;
    case Arbitration::won2:
        _nbrWon2++;
        WON:
        enhArbitrationDone();
        DEBUG_LOG("BUS SEND WON   0x%02x %ld us\n", _busState._master, _busState.microsSinceLastSyn());
        push({true,  STARTED,  _busState._master, _client, _client}); // send only to the arbitrating client
        push({false, RECEIVED, symbol,            _client, _client}); // do not send to arbitrating client
        _client=0;
        break;
    case Arbitration::lost1:
        _nbrLost1++;
        goto LOST;
    case Arbitration::lost2:
        _nbrLost2++;
        LOST:
        enhArbitrationDone();
        DEBUG_LOG("BUS SEND LOST  0x%02x 0x%02x %ld us\n", _busState._master, _busState._symbol, _busState.microsSinceLastSyn());
        push({true,  FAILED,   _busState._master, _client, _client}); // send only to the arbitrating client
        push({false, RECEIVED, symbol,            0,       _client}); // send to everybody    
        _client=0;
        break;
    case Arbitration::error:
        _nbrErrors++;
        enhArbitrationDone();
        push({true,  ERROR_EBUS, ERR_FRAMING, _client, _client}); // send only to the arbitrating client
        push({false, RECEIVED,   symbol,      0,       _client}); // send to everybody
        _client=0;
        break;
  }
}
