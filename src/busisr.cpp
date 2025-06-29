#include "busisr.hpp"

#if defined(EBUS_INTERNAL)
#include "main.hpp"

hw_timer_t* requestBusTimer = nullptr;

portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// This value can be adjusted if the bus request is not working as expected.
volatile uint16_t requestWindow = 4300;  // default 4300us
volatile bool requestBusPending = false;
volatile bool requestBusDone = false;

ebus::Bus* bus = nullptr;
ebus::Queue<uint8_t>* byteQueue = nullptr;
ebus::Handler* ebusHandler = nullptr;
ebus::ServiceRunner* serviceRunner = nullptr;

// ISR: Fires when a byte is received
void IRAM_ATTR onUartRx() {
  if (!byteQueue || !ebusHandler) return;
  // Read all available bytes as quickly as possible
  while (BusSer.available()) {
    // Pre-calculate start bit time only once per byte
    uint32_t micorsStartBit = micros() - 4167;  // 4167us = 2400 baud, 10 bit
    uint8_t byte = BusSer.read();

    // Handle bus request done flag
    if (requestBusDone) {
      requestBusDone = false;
      ebusHandler->busRequested();
    }

    // Push byte to queue (should be lock-free or ISR-safe)
    byteQueue->push(byte);

    // Handle bus request logic only if needed
    if (byte == ebus::sym_syn && ebusHandler->busRequest()) {
      portENTER_CRITICAL_ISR(&timerMux);
      int32_t delay = requestWindow - (micros() - micorsStartBit);
      if (delay > 0) {
        requestBusPending = true;
        timerAlarmWrite(requestBusTimer, delay, false);
        timerAlarmEnable(requestBusTimer);
      } else {
        // Only write if TX buffer is empty
        if (!BusSer.available()) {
          BusSer.write(ebusHandler->getAddress());
          requestBusPending = false;
          requestBusDone = true;
        }
      }
      portEXIT_CRITICAL_ISR(&timerMux);
    }
  }
}

// ISR: Write request byte at the exact time
void IRAM_ATTR onRequestBusTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  if (requestBusPending && !BusSer.available()) {
    BusSer.write(ebusHandler->getAddress());
    requestBusPending = false;
    requestBusDone = true;
  }
  timerAlarmDisable(requestBusTimer);
  portEXIT_CRITICAL_ISR(&timerMux);
}

void setupBusIsr() {
  // Initialize BusSer = Serial1
  BusSer.begin(2400, SERIAL_8N1, UART_RX, UART_TX);

  bus = new ebus::Bus(BusSer);
  byteQueue = new ebus::Queue<uint8_t>();
  ebusHandler = new ebus::Handler(bus, ebus::DEFAULT_ADDRESS);
  serviceRunner = new ebus::ServiceRunner(*ebusHandler, *byteQueue);

  // Attach the ISR to the UART receive event
  BusSer.onReceive(onUartRx);

  // Request bus timer: one-shot, armed as needed
  requestBusTimer = timerBegin(0, 160, true);
  timerAttachInterrupt(requestBusTimer, &onRequestBusTimer, true);
  timerAlarmDisable(requestBusTimer);
}

void setRequestWindow(const uint16_t& delay) { requestWindow = delay; }
#endif
