#include "busisr.hpp"

#if defined(EBUS_INTERNAL)

hw_timer_t* requestBusTimer = nullptr;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// This value can be adjusted if the bus request is not working as expected.
volatile uint16_t requestWindow = 4300;  // default 4300us
volatile bool requestBusPending = false;
volatile bool requestBusDone = false;

static HardwareSerial* hwSerial = nullptr;

ebus::Bus* bus = nullptr;
ebus::Queue<uint8_t>* byteQueue = nullptr;
ebus::Handler* ebusHandler = nullptr;
ebus::ServiceRunner* serviceRunner = nullptr;

// ISR: Fires when a byte is received
void IRAM_ATTR onUartRx() {
  if (!byteQueue || !ebusHandler) return;

  // Read all available bytes as quickly as possible
  while (hwSerial->available()) {
    // Pre-calculate start bit time only once per byte
    uint32_t micorsStartBit = micros() - 4167;  // 4167us = 2400 baud, 10 bit

    // Handle bus request done flag
    portENTER_CRITICAL_ISR(&timerMux);
    if (requestBusDone) {
      requestBusDone = false;
      ebusHandler->busRequested();
    }
    portEXIT_CRITICAL_ISR(&timerMux);

    uint8_t byte = hwSerial->read();
    // Push byte to queue (should be lock-free or ISR-safe)
    byteQueue->push(byte);

    // Handle bus request logic only if needed
    if (byte == ebus::sym_syn && ebusHandler->busRequest()) {
      int32_t delay = requestWindow - (micros() - micorsStartBit);
      if (delay > 0) {
        portENTER_CRITICAL_ISR(&timerMux);
        requestBusPending = true;
        portEXIT_CRITICAL_ISR(&timerMux);
        timerAlarmWrite(requestBusTimer, delay, false);
        timerAlarmEnable(requestBusTimer);
      } else {
        // Only write if TX buffer is empty
        if (!hwSerial->available()) {
          portENTER_CRITICAL_ISR(&timerMux);
          requestBusPending = false;
          requestBusDone = true;
          portEXIT_CRITICAL_ISR(&timerMux);
          hwSerial->write(ebusHandler->getAddress());
        }
      }
    }
  }
}

// ISR: Write request byte at the exact time
void IRAM_ATTR onRequestBusTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  if (requestBusPending && !hwSerial->available()) {
    requestBusPending = false;
    requestBusDone = true;
    hwSerial->write(ebusHandler->getAddress());
  }
  portEXIT_CRITICAL_ISR(&timerMux);
  timerAlarmDisable(requestBusTimer);
}

void setupBusIsr(HardwareSerial* serial, const int8_t& rxPin,
                 const int8_t& txPin) {
  if (serial) {
    hwSerial = serial;
    hwSerial->begin(2400, SERIAL_8N1, rxPin, txPin);

    bus = new ebus::Bus(*(hwSerial));
    byteQueue = new ebus::Queue<uint8_t>();
    ebusHandler = new ebus::Handler(bus, ebus::DEFAULT_ADDRESS);
    serviceRunner = new ebus::ServiceRunner(*ebusHandler, *byteQueue);

    // Attach the ISR to the UART receive event
    hwSerial->onReceive(onUartRx);

    // Request bus timer: one-shot, armed as needed
    uint32_t timer_clk = APB_CLK_FREQ;       // Usually 80 MHz
    uint16_t divider = timer_clk / 1000000;  // For 1us tick
    requestBusTimer = timerBegin(0, divider, true);
    timerAttachInterrupt(requestBusTimer, &onRequestBusTimer, true);
    timerAlarmDisable(requestBusTimer);
  }
}

void setRequestWindow(const uint16_t& delay) { requestWindow = delay; }
#endif
