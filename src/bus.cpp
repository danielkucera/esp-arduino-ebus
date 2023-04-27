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
void IRAM_ATTR BusType::receiveHandler() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xTaskNotifyFromISR( Bus._serialEventTask, 0, eNoAction, &xHigherPriorityTaskWoken );
  portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

void BusType::readDataFromSoftwareSerial(void *args)
{
    for(;;) {
        BaseType_t r=xTaskNotifyWait(0, 0, 0, portMAX_DELAY);
        {
          int avail = mySerial.available();
          if ( !avail) {
            // avoid this busy wait: delayMicroseconds(1+ MAX_FRAMEBITS * 1000000 / BAUD_RATE);

            // Need to wait for 1000000 / BAUD_RATE, rounded to the next upper digit.
            // delayMicroseconds is a busy wait, which blocks the CPU to do other things and 
            // could be the reason that the Wifi connection is blocked.
            // Instead of a busy wait, do the majority of the waiting with vTaskDelay. 
            // Because vTaskDelay is switching at Tick cycle, doing vTaskDelay(1) can wait 
            // anywhere between 0 Tick and 1 Ticks. Typically 1 Tick is 1 MiliSecond, although it 
            // depends on configuration. Do maximum 3 MiliSeconds (Ticks) with vTaskDelay and do 
            // the rest with a busy wait through delayMicroseconds()
            
            // Validate 1 Tick is 1 MiliSecond with a compile time assert
            static_assert (pdMS_TO_TICKS(1) == 1);

            unsigned int begin = micros();
            vTaskDelay(3);
            unsigned int end = micros();
            delayMicroseconds(4167-(end - begin));
            avail = mySerial.available();
          }
          if (avail){
              uint8_t symbol = mySerial.read();
              Bus.receive(symbol, mySerial.readStartBitTimeStamp());
          }  
        }
    }
    vTaskDelete(NULL);
}
#endif

void BusType::begin() {

#ifdef ESP32
  Serial.begin(2400, SERIAL_8N1, -1, 20); // used for writing
  mySerial.enableStartBitTimeStampRecording(true);
  mySerial.begin(2400, SWSERIAL_8N1, 21, -1); // used for reading
  mySerial.enableTx(false);
  mySerial.enableIntTx(false);
#elif defined(ESP8266)
  Serial.begin(2400);
#endif

#if USE_ASYNCHRONOUS
  _queue = xQueueCreate(QUEUE_SIZE, sizeof(data));
  xTaskCreateUniversal(BusType::readDataFromSoftwareSerial, "_serialEventQueue", SERIAL_EVENT_TASK_STACK_SIZE, this, SERIAL_EVENT_TASK_PRIORITY, &_serialEventTask, SERIAL_EVENT_TASK_RUNNING_CORE);
  
  mySerial.onReceive(BusType::receiveHandler);
#else
  Serial.setRxBufferSize(RXBUFFERSIZE);
#endif    
}

void BusType::end() {
  Serial.end();
#ifdef ESP32
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
