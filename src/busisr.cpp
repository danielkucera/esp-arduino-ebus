#include "busisr.hpp"

#if defined(EBUS_INTERNAL) && !defined(USE_IDF_UART)

ebus::Bus* bus = nullptr;
ebus::Handler* ebusHandler = nullptr;
ebus::ServiceRunner* serviceRunner = nullptr;
static ebus::Queue<uint8_t>* byteQueue = nullptr;

void setupBusIsr(const int rx_pin, const int tx_pin) {
  // Create queue and bus
  static HardwareSerial& serial = BusSer;  // or Serial1, as appropriate
  byteQueue = new ebus::Queue<uint8_t>();
  bus = new ebus::Bus(serial, *byteQueue);

  // Create handler and service runner
  ebusHandler = new ebus::Handler(bus, ebus::DEFAULT_ADDRESS);
  bus->setHandler(ebusHandler);
  serviceRunner = new ebus::ServiceRunner(*ebusHandler, *byteQueue);

  // Start bus
  bus->begin(2400, rx_pin, tx_pin);
}

void setRequestWindow(const uint16_t& delay) {
  if (bus) bus->setRequestWindow(delay);
}

#endif
