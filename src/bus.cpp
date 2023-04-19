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
  _queue = xQueueCreate(RXBUFFERSIZE, sizeof(data));
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
    bool result =  xQueueReceive(_queue, &d, 0) == pdTRUE; 
    return result;

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
        if (client && arbitration.start(busState, arbitration_address)) {     
          push({false, RECEIVED, byte, client}); // do not send to arbitration client
        }
        else {
          client = 0;
          push({false, RECEIVED, byte, 0}); // send to everybody
        }
        break;
    case Arbitration::arbitrating:
    {
        push({false, RECEIVED, byte, client}); // do not send to arbitration client
        break;
    }
    case Arbitration::won:
    {
        DEBUG_LOG("ARB SEND WON   0x%02x %ld us\n", busState._master, busState.microsSinceLastSyn());
        push({false, RECEIVED, byte, client}); // do not send to arbitration client
        push({true, STARTED, busState._master, client}); // send only to the arbitrating client
        enhArbitrationDone(client);
        client=0;
        break;
    }
    case Arbitration::lost:
    {
        DEBUG_LOG("ARB SEND LOST  0x%02x 0x%02x %ld us\n", busState._master, busState._byte, busState.microsSinceLastSyn());
        push({true, FAILED, busState._master, client}); // send only to the arbitrating client
        push({false, RECEIVED, byte, 0}); // send to everybody    
        enhArbitrationDone(client);
        client=0;
        break;
    }
    case Arbitration::error:
    {
        push({true, ERROR_EBUS, ERR_FRAMING, client}); // send only to the arbitrating client
        push({false, RECEIVED, byte, 0}); // send to everybody
        enhArbitrationDone(client);
        client=0;
        break;
    }
  }
}


  //static EBusState   busState;
  //static Arbitration arbitration;
  // We received a byte. 
  // Handle arbitration in this CB; has to be fast enough to be done for the next character
  // We can't send data to WifiClient
  // - likely not thread safe
  // - would take too long
  // Instead regular wifi client communication needs to happen in the main loop, untill maybe WifiClient can be put in
  // a task and we communicate to WifiClient with messages
  // BusState should only be used from this thread, only relevant case
  // HardwareSerial is thread safe, no problem to call write and read from different threads
  // send results to mainloop
  // -> each byte as it needs to be forwarded to other clients
  // -> the state of the arbitration

  // What is needed in the main loop?
  // - Each client should receive all chars that incoming on the bus
  // - Need to maintain the correct order of bytes, both for
  //   receiving clients as for arbitrating clients
  // - enhanced clients need to receive data in the enhanced format
  // - If an arbitration is ongoing for client X
  //   + Client X should not receive the chars that are part of the arbitration
  //   + Client X should receive the result of the arbitration
  //     and then it should receive the bytes as usual
  // How do we communicate to main loop?
  // - for each byte, 
  //   + the byte
  //   + is it a regular byte, or the result of arbitration
  //   + a client id in case an arbitration is ongoing