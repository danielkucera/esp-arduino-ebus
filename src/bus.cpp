#include "main.hpp"
#include "bus.hpp"
#include "ebusstate.hpp"
#include "arbitration.hpp"
#include "enhanced.hpp"
#include "queue"

BusType Bus;

#ifdef USE_ASYNCHRONOUS
void BusType::OnReceiveCB()
{
  uint8_t byte = Serial.read();
  Bus.receive(byte);
}

void BusType::OnReceiveErrorCB(hardwareSerial_error_t e)
{
  if (e != UART_BREAK_ERROR){
    DEBUG_LOG("OnReceiveErrorCB %i\n", e);
  }
}
#endif

BusType::BusType()
{
#ifdef USE_ASYNCHRONOUS
  Serial.onReceive(OnReceiveCB, false);
  Serial.onReceiveError(OnReceiveErrorCB);
  _queue = xQueueCreate(QUEUE_SIZE, sizeof(data));
#endif    
}

BusType::~BusType(){
#ifdef USE_ASYNCHRONOUS
  vQueueDelete(_queue);
#endif 
}

bool BusType::read(data& d)
{
#ifdef USE_ASYNCHRONOUS
    return xQueueReceive(_queue, &d, 0) == pdTRUE; 
#else
    if (Serial.available()){
        uint8_t byte = Serial.read();
        receive(byte);
    }
    if (_queue.size() > 0) {
        d = _queue.front();
        _queue.pop();
        return true;
    }
    return false;
#endif
}

void BusType::push(const data& d)
{
#ifdef USE_ASYNCHRONOUS
    xQueueSendToBack(_queue, &d, 0); 
#else
    _queue.push(d);
#endif
}

void BusType::receive(uint8_t byte)
{
  static EBusState   busState;
  static Arbitration arbitration;
  static WiFiClient*  client = 0;

  busState.data(byte);
  Arbitration::state state = arbitration.data(busState, byte);
  switch (state) {
    case Arbitration::none:
        uint8_t arbitration_address;
        client = enhArbitrationRequested(arbitration_address);
        if (client) {
          if (arbitration.start(busState, arbitration_address)) {     
            DEBUG_LOG("BUS START SUC  0x%02x %ld us\n", byte, busState.microsSinceLastSyn());
          }
          else {
            DEBUG_LOG("BUS START FAI  0x%02x %ld us\n", byte, busState.microsSinceLastSyn());
          }
        }
        push({false, RECEIVED, byte, 0, client}); // send to everybody
        break;
    case Arbitration::arbitrating:
        DEBUG_LOG("BUS ARBITRATIN 0x%02x %ld us\n", byte, busState.microsSinceLastSyn());
        push({false, RECEIVED, byte, client, client}); // do not send to arbitration client
        break;
    case Arbitration::won:
        enhArbitrationDone();
        DEBUG_LOG("BUS SEND WON   0x%02x %ld us\n", busState._master, busState.microsSinceLastSyn());
        push({true,  STARTED,  busState._master, client, client}); // send only to the arbitrating client
        push({false, RECEIVED, byte,             client, client}); // do not send to arbitrating client
        client=0;
        break;
    case Arbitration::lost:
        enhArbitrationDone();
        DEBUG_LOG("BUS SEND LOST  0x%02x 0x%02x %ld us\n", busState._master, busState._byte, busState.microsSinceLastSyn());
        push({true,  FAILED,   busState._master, client, client}); // send only to the arbitrating client
        push({false, RECEIVED, byte,             0,      client}); // send to everybody    
        client=0;
        break;
    case Arbitration::error:
        enhArbitrationDone();
        push({true,  ERROR_EBUS, ERR_FRAMING, client, client}); // send only to the arbitrating client
        push({false, RECEIVED,   byte,        0,      client}); // send to everybody
        client=0;
        break;
  }
}
