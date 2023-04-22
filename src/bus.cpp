#include "main.hpp"
#include "bus.hpp"
#include "enhanced.hpp"
#include "queue"

BusType Bus;

BusType::BusType()
  : _client(0) 
  , _nbrRestarts(0)
  , _nbrArbitrations(0) {
}

BusType::~BusType() {
  end();
}

void BusType::begin() {
  Serial.setRxBufferSize(RXBUFFERSIZE);

#ifdef ESP32
  Serial.begin(2400, SERIAL_8N1, 21, 20);
  Serial.setRxFIFOFull(1); // ESP32 in Arduino uses heuristics to sometimes set RxFIFOFull to 1, better to be explicit
#elif defined(ESP8266)
  Serial.begin(2400);
#endif

#if USE_ASYNCHRONOUS
  Serial.onReceive(OnReceiveCB, false);
  Serial.onReceiveError(OnReceiveErrorCB);
  _queue = xQueueCreate(QUEUE_SIZE, sizeof(data));
#endif    
}

void BusType::end() {
#if USE_ASYNCHRONOUS
  vQueueDelete(_queue);
#endif 
}

#if USE_ASYNCHRONOUS
void BusType::OnReceiveCB() {
  uint8_t symbol = Serial.read();
  Bus.receive(symbol);
}

void BusType::OnReceiveErrorCB(hardwareSerial_error_t e) {
  if (e != UART_BREAK_ERROR){
    DEBUG_LOG("OnReceiveErrorCB %i\n", e);
  }
}
#endif

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
        receive(symbol);
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

void BusType::receive(uint8_t symbol) {
  _busState.data(symbol);
  Arbitration::state state = _arbitration.data(_busState, symbol);
  switch (state) {
    case Arbitration::restart:
        _nbrRestarts++;
    case Arbitration::none:
        uint8_t arbitration_address;
        _client = enhArbitrationRequested(arbitration_address);
        if (_client) {
          if (_arbitration.start(_busState, arbitration_address)) {   
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
    case Arbitration::won:
        enhArbitrationDone();
        DEBUG_LOG("BUS SEND WON   0x%02x %ld us\n", _busState._master, _busState.microsSinceLastSyn());
        push({true,  STARTED,  _busState._master, _client, _client}); // send only to the arbitrating client
        push({false, RECEIVED, symbol,            _client, _client}); // do not send to arbitrating client
        _client=0;
        break;
    case Arbitration::lost:
        enhArbitrationDone();
        DEBUG_LOG("BUS SEND LOST  0x%02x 0x%02x %ld us\n", _busState._master, _busState._symbol, _busState.microsSinceLastSyn());
        push({true,  FAILED,   _busState._master, _client, _client}); // send only to the arbitrating client
        push({false, RECEIVED, symbol,            0,       _client}); // send to everybody    
        _client=0;
        break;
    case Arbitration::error:
        enhArbitrationDone();
        push({true,  ERROR_EBUS, ERR_FRAMING, _client, _client}); // send only to the arbitrating client
        push({false, RECEIVED,   symbol,      0,       _client}); // send to everybody
        _client=0;
        break;
  }
}
