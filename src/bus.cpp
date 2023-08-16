#include "main.hpp"
#include "bus.hpp"
#include "enhanced.hpp"
#include "queue"

// For ESP's based on FreeRTOS we can optimize the arbitration timing.
// With SoftwareSerial we get notified with an callback that the 
// signal has changed. SoftwareSerial itself can and does know the 
// exact timing of the start bit. Use this for the timing of the 
// arbitration. SoftwareSerial seems to have trouble with writing 
// and reading at the same time. Hence use SoftwareSerial only for 
// reading. For writing use HardwareSerial.
#if USE_SOFTWARE_SERIAL
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
void IRAM_ATTR BusType::receiveHandler() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR( Bus._serialEventTask, &xHigherPriorityTaskWoken );
  portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

void BusType::readDataFromSoftwareSerial(void *args)
{
    for(;;) {
        BaseType_t r=ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        {
          // For SoftwareSerial; 
          // The method "available" always evaluates all the interrupts received
          // The method "read" only evaluates the interrupts received if there is no byte available
          int avail = mySerial.available();
          if ( !avail && r==1) {
            // avoid this busy wait: delayMicroseconds(1+ MAX_FRAMEBITS * 1000000 / BAUD_RATE);

            // Need to wait for 1000000 / BAUD_RATE, rounded to the next upper digit.
            // delayMicroseconds is a busy wait, which blocks the CPU to do other things and 
            // could be the reason that the Wifi connection is blocked.
            // Instead of a busy wait, do the majority of the waiting with vTaskDelay. 
            // Because vTaskDelay is switching at Tick cycle, doing vTaskDelay(1) can wait 
            // anywhere between 0 Tick and 1 Ticks. On esp32 Arduino  1 Tick is 1 MilliSecond, 
            // although it depends on configuration. 
            
            // Validate 1 Tick is 1 MilliSecond with a compile time assert
            static_assert (pdMS_TO_TICKS(1) == 1);
            static_assert (sizeof(uint32_t) == sizeof(unsigned long));

            // We need to poll mySerial for availability of a byte. Testing has shown that from 1 millisecond
            // onward we need to check for incoming data every 500 micros. We have to wait using vTaskDelay
            // to allow the processor to do other things, however that only allows millisecond resolution.
            // To work around, split the polling in two sections:
            // 1) Wait for 500 micros using busy wait with delayMicroseconds
            // 2) Wait the rest of the timeslice, which will be about 500 micros, using vTaskDelay
            unsigned long begin = micros();
            vTaskDelay(pdMS_TO_TICKS(1));
            avail = mySerial.available();

            // How was the delay until now?
            unsigned long delayed = micros() - begin;

            // Loop till the maximum duration of 1 byte (4167 micros from begin)
            // and check every 500 micros, using combination of delayMicroseconds(500) and
            // vTaskDelay(pdMS_TO_TICKS(1)) . The vTaskDelay will wait till the end of the 
            // current timeslice, which is typically about 500 micros away, because the 
            // previous vTaskDelay makes sure the code is already synced to this tick
            // Assumption: time needed for mySerial.available() is less than 500 micros.
            while (delayed < 4167 && !avail) {
              if (4167 - delayed > 1000) { // Need to wait more than 1000 micros?
                delayMicroseconds(500);
                avail = mySerial.available();
                if (!avail) {
                  vTaskDelay(pdMS_TO_TICKS(1));
                }                
              }
              else { // Otherwise spend the remaining wait with delayMicroseconds
                unsigned long delay = 4167-delayed<500?4167-delayed:500;
                delayMicroseconds(delay);
              }
              avail = mySerial.available();
              delayed = micros() - begin;
            }
          }
          if (avail){
              int symbol = mySerial.read();
              Bus.receive(symbol, mySerial.readStartBitTimeStamp());
          }  
        }
    }
    vTaskDelete(NULL);
}
#endif

void BusType::begin() {

#if USE_SOFTWARE_SERIAL
 #if defined(ESP32)
  Serial.begin(2400, SERIAL_8N1, -1, UART_TX); // used for writing
 #elif defined(ESP8266)
  Serial.begin(2400, SERIAL_8N1, SERIAL_TX_ONLY, UART_TX);
 #endif
  mySerial.enableStartBitTimeStampRecording(true);
  mySerial.enableTx(false);
  mySerial.enableIntTx(false);
  mySerial.begin(2400, SWSERIAL_8N1, UART_RX, -1, false, RXBUFFERSIZE); // used for reading
#else
  Serial.setRxBufferSize(RXBUFFERSIZE);
 #if defined(ESP32)
  Serial.begin(2400, SERIAL_8N1, UART_RX, UART_TX); // used for writing
  Serial.setRxFIFOFull(1);
 #elif defined(ESP8266)
  Serial.begin(2400);
 #endif
#endif

#if USE_ASYNCHRONOUS
  _queue = xQueueCreate(QUEUE_SIZE, sizeof(data));
  xTaskCreateUniversal(BusType::readDataFromSoftwareSerial, "_serialEventQueue", SERIAL_EVENT_TASK_STACK_SIZE, this, SERIAL_EVENT_TASK_PRIORITY, &_serialEventTask, SERIAL_EVENT_TASK_RUNNING_CORE);
  mySerial.onReceive(BusType::receiveHandler);
#endif    
}

void BusType::end() {
  Serial.end();
#if USE_SOFTWARE_SERIAL
  mySerial.end();
#endif

#if USE_ASYNCHRONOUS
  vQueueDelete(_queue);
  _queue=0;
  
  vTaskDelete(_serialEventTask);
  _serialEventTask=0;
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
  #if USE_SOFTWARE_SERIAL
    if (mySerial.available()){
        uint8_t symbol = mySerial.read();
        receive(symbol, mySerial.readStartBitTimeStamp());
    }
  #else
    if (Serial.available()){
        uint8_t symbol = Serial.read();
        receive(symbol, micros());
    }
  #endif
    if (_queue.size() > 0) {
        d = _queue.front();
        _queue.pop();
        return true;
    }
    return false;
#endif
}

int BusType::available() {
#if USE_SOFTWARE_SERIAL
  return mySerial.available(); 
#else
  return Serial.available();
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
          switch (_arbitration.start(_busState, arbitration_address, startBitTime)) {
            case Arbitration::started:
              _nbrArbitrations++;
              DEBUG_LOG("BUS START SUCC 0x%02x %lu us\n", symbol, _busState.microsSinceLastSyn());
            break;
            case Arbitration::late:
              _nbrLate++;
            case Arbitration::not_started:
              DEBUG_LOG("BUS START WAIT 0x%02x %lu us\n", symbol, _busState.microsSinceLastSyn());
          }
        }
        push({false, RECEIVED, symbol, 0, _client}); // send to everybody. ebusd needs the SYN to get in the right mood
        break;
    case Arbitration::arbitrating:
        DEBUG_LOG("BUS ARBITRATIN 0x%02x %lu us\n", symbol, _busState.microsSinceLastSyn());
        push({false, RECEIVED, symbol, _client, _client}); // do not send to arbitration client
        break;
    case Arbitration::won1:
        _nbrWon1++;
        goto WON;
    case Arbitration::won2:
        _nbrWon2++;
        WON:
        enhArbitrationDone();
        DEBUG_LOG("BUS SEND WON   0x%02x %lu us\n", _busState._master, _busState.microsSinceLastSyn());
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
        DEBUG_LOG("BUS SEND LOST  0x%02x 0x%02x %lu us\n", _busState._master, _busState._symbol, _busState.microsSinceLastSyn());
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
